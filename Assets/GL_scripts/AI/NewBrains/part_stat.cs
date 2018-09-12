using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class part_stat : MonoBehaviour {

	public Vector2[] points;
	public GameObject[] Bpoints;
	/// <summary>
	/// Здесь нарушение порядка контейнеров, потому что расставляем пока как в античности - без групп
	/// Группы пока выполнять функцию объединения под контролем одного офицера нескольких полков
	/// Затем группы будут отражать разнородное феодальное войско, кде каждая группа разных размеров будет отрядом 
	/// землевладельца 
	/// </summary>
	public List <reg_stat> regiments;
	public List <group_stat> groups;


	public void ram ()
	{
		points = new Vector2[4];
		Bpoints = new GameObject[4];
		regiments = new List<reg_stat> ();
	}

	public void make_position ()
	{
		/// Считаем вместе с промежутками
		float comm_length = calc_common_length (1f);
		float part_f_length = Mathf.Abs (points [0].x - points [1].x);

		if (regiments.Count > 1) {
			Debug.Log ("Start"); 
			Debug.Log ("Part lengf =" + comm_length);
			Debug.Log ("part_f_length =" + part_f_length);
		}

		if (comm_length < part_f_length)
		{
			/// Буфер для длины именно полков в линии
			float temp = 0;

			/// Считаем без промежутков 
			comm_length =  calc_common_length (0f);
			float dist = (part_f_length - comm_length) / (regiments.Count + 1);
			/// Случай, когда полков мало и они могут построиться в линию.
			Debug.Log (regiments.Count);
			for (int i = 0; i < regiments.Count; i++) {
				/// Формула такая 
				/// Берем точку на границе. Добавляем промежутки между полками и 
				/// Учитываем что центр полка смещен от края на половину своей длины
				regiments [i].new_pos.x = points [0].x + dist * (1 + i) + temp + AI.Scale * regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine/2; 
				regiments[i].new_pos.y = points[0].y;
				//Debug.Log (regiments [i].new_pos.x);
				temp += regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine * AI.Scale;
			}
		}

		if (comm_length > part_f_length) {

			if (regiments.Count > 1) {
				Debug.Log ("two lines"); 
			}
			make_two_chas_lines ();
		}
		/// Случай, если в две линии хаотично
	}

	private void make_two_chas_lines ()
	{
		
		float comm_lengt = calc_common_length (1f);
		float part_f_length = Mathf.Abs (points [0].x - points [1].x);
		/// Буфер для длины именно полков в линии
		float temp = 0;
		/// Интервал иежду полками
		//float dist = (part_f_length - comm_length) / (regiments.Count + 1);
		/// Случай, когда полков мало и они могут построиться в линию.
		/// Для определения второй линии
		// Немного отступаем от края
		float d_temp_dist = 1f;
		int j = 0;
		while (d_temp_dist < part_f_length) {
			d_temp_dist += AI.Scale * regiments [j].tablereg.regFirstLine * regiments [j].tablereg.regDistanseLine ;
			d_temp_dist += 1;
			j++;
		}

		//Debug.Log ("--------------");
		//Debug.Log (d_temp_dist);
		//Debug.Log (part_f_length);
		// Теперь j - номер полка, который последний в первой линии

		comm_lengt = calc_common_length (j, 0);
		float dist = 1f;
		for (int i = 0; i < j; i++) {
			/// Формула такая 
			/// Берем точку на границе. Добавляем промежутки между полками и 
			/// Учитываем что центр полка смещен от края на половину своей длины
			regiments [i].new_pos.x = points [0].x + dist * (1 + i) + temp +  AI.Scale * regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine/2; 
			regiments[i].new_pos.y = points[0].y;
			temp += AI.Scale * regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine;
		}
		temp = 0; 
		for (int i = j; i < regiments.Count; i++) {
			regiments [i].new_pos.x = points [0].x + dist * (1 + (i- j )) + temp + AI.Scale * regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine/2; 
			regiments[i].new_pos.y = points[0].y + 0.333f *  (points[3].y -  points[0].y) ;
			temp +=  AI.Scale * regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine;
		}
	}


	private float calc_common_length (float dist)
	{
		float res = dist;
		for (int i = 0; i < regiments.Count; i++) 
		{
			res += regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine * AI.Scale;
			res += dist;
		}
		return res;
	}

	private float calc_common_length (int j, int y)
	{
		float res = 0;
		for (int i = 0; i < j; i++) 
		{
			res += regiments [i].tablereg.regFirstLine * regiments [i].tablereg.regDistanseLine * AI.Scale;
		}
		return res;
	}

}
/*struct mypart
{
	/// <summary>
	///  1 - авангард
	///  2 - 3 - 4 правый, центр, левый
	///  5 -- резерв
	/// </summary>
	public int type;
	public int number;
}
*/