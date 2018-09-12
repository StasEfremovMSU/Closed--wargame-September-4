using System.Collections;
using System.Collections.Generic;
using UnityEngine;

public class LoadBattle : MonoBehaviour
{
    /*
    public GameObject[] myobject = new GameObject[2];

    // Всё для загрузки карты и сражения
    public void loadBattle()
    {
        // Загрузить список солдат
        // Загрузить список полков
        // Расставить полки

            List<temm> reader2 = LoadTxt.loadTxt("map1.txt");
            loadRegiment LoadData = new loadRegiment(LoadTxt.number);

            for (int i = 0; i < reader2.Count; i++)
            {
                 LoadData.addregiment (reader2[i].x);
            }

            LoadData.numReg = LoadTxt.number;
            
            mainStatic.regiment_position = new Vector3[LoadTxt.number];
           


            // Уникальное id солдата
            int n_obj = 0;
            // Цикл по количеству полков
            for (int n = 0; n < LoadData.numReg; n++)
            {
                // Передаем данные п статический массив
                LoadData.putregiment(n);
                int t_x, t_z; t_x = t_z = 0;
                // Создаем кубики в построении
                for (int i = 0; i < LoadData.size[n]; i++)
                {

                    if (t_x == LoadData.w[n])
                    {
                        t_x = 0; t_z++;
                    }
                    Vector3 v, v2; float tx = 0; float tz = 0;
                    v.y = 0.5f;
                    v.x = t_x * 3;
                    v.z = t_z * 3;
                    tx = v.x - LoadData.w[n] / 2 * 3; tz = v.z;
                    float a = 0; a = LoadData.angle[n];
                    v.x = tx * Mathf.Cos(a) - tz * Mathf.Sin(a);
                    v.z = tx * Mathf.Sin(a) + tz * Mathf.Cos(a);
                    v2.x = v.x + LoadData.x[n];
                    v2.y = v.y + LoadData.y[n];
                    v2.z = v.z + LoadData.z[n];

                    mainStatic.regiment_stat[n].soldiers.Add(Instantiate(myobject[0], v2, Quaternion.identity).GetComponent<UnitSoldiers>());

                    mainStatic.regiment_stat[n].soldiers[i].new_position = v2;
                    mainStatic.regiment_position[n].x = LoadData.x[n];
                    mainStatic.regiment_position[n].y = LoadData.y[n];
                    mainStatic.regiment_position[n].z = LoadData.z[n];

                    

                    mainStatic.regiment_stat[n].angle =  LoadData.angle[n];

                    mainStatic.regiment_stat[n].soldiers[i].k1 = t_x;
                    mainStatic.regiment_stat[n].soldiers[i].k2 = t_z;
                    mainStatic.regiment_stat[n].soldiers[i].angle = LoadData.angle[n];
                    mainStatic.regiment_stat[n].soldiers[i].matrix_position = new Vector3(v.x, v.y, v.z);

                    mainStatic.regiment_stat[n].soldiers[i].gl_number = n_obj;

                    mainStatic.regiment_stat[n].soldiers[i].platerControl = false;
                    mainStatic.regiment_stat[n].figthMas = new List<int>();

                    if (mainStatic.regiment_stat[n].command == 0)
                    {
                        mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<Renderer>().material.color = Color.blue;
                    }
                    else 
                    {
                        mainStatic.regiment_stat[n].soldiers[i].transform.GetComponent<Renderer>().material.color = Color.green;
                    }

                    // Указали номер, в котором
                    mainStatic.regiment_stat[n].soldiers[i].numReg = n;
                    mainStatic.regiment_stat[n].soldiers[i].command = mainStatic.regiment_stat[n].command;

                    // Единичный вектор, в направлении взгляда полка
                    //mainStatic.regiment_stat[n].forw = mainStatic.makeTarget(new Vector3(0, 1,0), mainStatic.regiment_stat[n].angle);
                    t_x++;
                    n_obj++;

                }

            }

            for (int t = 0; t < LoadData.numReg; t++)
            {
                mainStatic.updateRegimentPosition(t);
            }
            mainStatic.flag = false;

            

        }
     * */
}
