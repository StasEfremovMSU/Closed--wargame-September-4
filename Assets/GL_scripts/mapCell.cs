using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public static class mapCell  {
    
    public static List <cell_data> cell;
    /// <summary>
    ///  Количество клеток по горизонтали 
    /// </summary>
    /// Только четные!
    public static int M = 20;
    /// <summary>
    ///  Количество клеток по вертикали
    /// </summary>
    public static int N = 20;
    static int CellSize = 20;// Если фронты не большие
    /// от -500 до 500
    public static void  mapCellconstr()
    {
        /// объявляем список клеток
        cell = new List<cell_data>();
        for (int w = 0; w < mapCell.M * mapCell.N; w++)
        {
            cell_data data = new cell_data();
            cell.Add(data);
        }
    }
    
    /// <summary>
    /// Положить все полки по клеткам
    /// </summary>
    public static void PutAllReg()
    {
        for (int t = 0; t < mainStatic.regiment_stat.Count; t++)
        {
            int x, z; x = z = 0;
			Vector2 temp = GetCell (t);
			x = (int) temp.x;
			z = (int)temp.y;
            mypai pair;
            pair.regNum  = t;
            pair.command = mainStatic.regiment_stat[t].command;
            cell[(x + z * M)].data.Add(pair);
            mainStatic.regiment_stat[t].cell_number = x + z * M;
            mainStatic.regiment_stat[t].cellX = x;
            mainStatic.regiment_stat[t].cellZ = z;
        }
    }

    public static void RecountMoving()
    {
        for (int i = 0; i < mainStatic.regiment_stat.Count; i++)
        {
            if (mainStatic.regiment_stat[i].moveCell == true)
            {
                /// Убрать из старой клетки
                mypai NewPai;
                NewPai.command = mainStatic.regiment_stat[i].command;
                NewPai.regNum = i;
                int cellNumber = mainStatic.regiment_stat[i].cellX + mainStatic.regiment_stat[i].cellZ * M;
                cell_data NewCell = new cell_data ();
                // Переложим чтобы не было пустых
                // Кладем в буфер все кроме двигаемого
                for (int t = 0; t < cell[cellNumber].data.Count ; t++)
                {
                    if (t != i )
                    {
                        mypai temp;
                        temp.regNum = cell[cellNumber].data[t].regNum;
                        temp.command = cell[cellNumber].data[t].command;
                        NewCell.data.Add (temp);
                    }
                }
                cell[cellNumber].data.Clear();
                cell[cellNumber] = NewCell;
                /// Положить в новую клетку
                mypai tempp;
                tempp.command = mainStatic.regiment_stat[i].command;
                tempp.regNum = i;
                cell[(mainStatic.regiment_stat[i].cellX + mainStatic.regiment_stat[i].cellZ * M)].data.Add(tempp);
            }
        }
    }

	/// <summary>
	/// Функция, которая дает список номеров полков, которые находятся рядом (длдя расчета видимости)
	/// </summary>
	/// <returns>The near reg.</returns>
	/// <param name="num">Number.</param>
	public static List <int> GetNearReg (int num)
	{
		List <int> res = new List<int> ();
		/// Вытаскиваем номер ячейки, где лежит полк
		Vector2 temp = GetCell (num);
		int x = (int)temp.x;
		int y = (int)temp.y;
		/// Как далеко смотрим по сторонам
		int DEF = 5;
		for (int i = -DEF; i < DEF; i++)
			for (int k = -DEF; k < DEF; k++) {
				//Получаем данные из конкретной клетки
				cell_data r = GetCell (x + i, y + k);
				for (int t = 0; t < r.data.Count; t++) {
					res.Add (r.data[t].regNum);
				}
			}
		return res;
	}

	/// <summary>
	/// Получить номер клетки по координатам x z 
	/// </summary>S
	/// <returns>The cell.</returns>
	/// <param name="x">The x coordinate.</param>
	/// <param name="z">The z coordinate.</param>
	private static cell_data GetCell(int x, int z)
	{
		return cell[x + z * M];
	}

	/// <summary>
	///  Принимает на вход номер полка, возвращает клетку, в котрой он находится
	/// </summary>
	/// <returns>The cell.</returns>
	/// <param name="t">T.</param>
	private static Vector2 GetCell(int t)
	{
		Vector2 res = new Vector2 ();
		int x, z; x = z = 0;
		x = (int) (mainStatic.regiment_stat[t].regPosition.x  - mainStatic.regiment_stat[t].regPosition.x %  CellSize) / CellSize;
		z = (int) (mainStatic.regiment_stat[t].regPosition.z - mainStatic.regiment_stat[t].regPosition.z % CellSize) / CellSize;

		res.x = x;
		res.y = z;
		return res;
	}

}

public class cell_data
{
    public List<mypai> data;
    public cell_data()
    {
        data = new List<mypai>();
    }
}


public struct mypai
{
    public int regNum;
    public int command;
}